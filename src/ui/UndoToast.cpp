#include "UndoToast.h"
#include "NapkinStyle.h"
#include "Tokens.h"

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
    // The Undo pill sits as far from the toast's edge as from its top and
    // bottom; flat, it read as a word 10px from the edge, not as a button.
    layout->setContentsMargins(18, 8, 8, 8);
    layout->setSpacing(14);

    message_ = new QLabel;
    auto* undo = new QPushButton(tr("Undo"));
    undoButton_ = undo;
    undo->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("pill"));
    undo->setCursor(Qt::PointingHandCursor);
    undo->setAccessibleName(tr("Undo the last deletion"));
    undo->setToolTip(tr("Undo (Ctrl+Z)"));

    layout->addWidget(message_);
    layout->addWidget(undo);

    setProperty(NapkinStyle::kSurfaceProperty, true);   // the pill is cut out of it
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &UndoToast::expireDue);

    informTimer_ = new QTimer(this);
    informTimer_->setSingleShot(true);
    connect(informTimer_, &QTimer::timeout, this, [this] {
        informing_ = false;
        if (!hovered_) resume();
        showTop();
    });

    connect(undo, &QPushButton::clicked, this, [this] { undoNow(); });

    hide();
}

QSet<QString> UndoToast::protectedHashes() const
{
    QSet<QString> all;
    for (const auto& o : offers_) all.unite(o.protects);
    return all;
}

int UndoToast::offer(const QString& message, std::function<void()> undo,
                     const QSet<QString>& protects)
{
    // An acknowledgement standing in front of the old top is superseded: the
    // new offer is the thing to see now.
    if (informing_) {
        informing_ = false;
        informTimer_->stop();
        if (!hovered_) resume();
    }
    if (!undo) { inform(message); return 0; }

    Offer o{nextId_++, message, std::move(undo), protects, QDeadlineTimer(QDeadlineTimer::Forever),
            visibleMs_};
    if (!hovered_) o.deadline = QDeadlineTimer(visibleMs_);
    const int id = o.id;
    offers_.push_back(std::move(o));
    showTop();
    scheduleNextExpiry();
    return id;
}

void UndoToast::withdraw(int id)
{
    const size_t before = offers_.size();
    std::erase_if(offers_, [id](const Offer& o) { return o.id == id; });
    if (offers_.size() == before) return;
    showTop();
    scheduleNextExpiry();
    emit expired();
}

bool UndoToast::undoNow()
{
    if (offers_.empty()) return false;
    Offer top = std::move(offers_.back());
    offers_.pop_back();
    if (informing_) { informing_ = false; informTimer_->stop(); if (!hovered_) resume(); }
    showTop();
    scheduleNextExpiry();
    top.undo();
    emit undone();
    return true;
}

// An informational message reports something that destroyed nothing, so it must
// not cancel an offer the user may still be reaching for. Spelled
// `offer(message, nullptr)`, it once did exactly that: a copy — or a paste that
// found nothing on the clipboard — silently threw away the Undo for a delete
// made two seconds earlier, and Ctrl+Z then did nothing. The offers are paused
// behind it for a moment and come back with what is left of their countdowns.
void UndoToast::inform(const QString& message)
{
    informing_ = true;
    pause();
    message_->setText(message);
    undoButton_->setVisible(false);
    layout()->setContentsMargins(18, 10, 18, 10);   // words alone: even all round
    adjustSize();
    reposition();
    show();
    raise();
    if (!hovered_) informTimer_->start(kInformMs);
}

void UndoToast::dismiss()
{
    timer_->stop();
    informTimer_->stop();
    informing_ = false;
    const bool had = !offers_.empty();
    offers_.clear();
    hide();
    // Expiring releases whatever the offers were holding, exactly as taking
    // them up would.
    if (had) emit expired();
}

void UndoToast::pause()
{
    timer_->stop();
    for (auto& o : offers_)
        if (!o.deadline.isForever()) {
            o.remainingMs = std::max<qint64>(1, o.deadline.remainingTime());
            o.deadline = QDeadlineTimer(QDeadlineTimer::Forever);
        }
}

void UndoToast::resume()
{
    for (auto& o : offers_)
        if (o.deadline.isForever()) o.deadline = QDeadlineTimer(o.remainingMs);
    scheduleNextExpiry();
}

void UndoToast::scheduleNextExpiry()
{
    timer_->stop();
    qint64 soonest = -1;
    for (const auto& o : offers_) {
        if (o.deadline.isForever()) continue;
        const qint64 left = std::max<qint64>(0, o.deadline.remainingTime());
        if (soonest < 0 || left < soonest) soonest = left;
    }
    if (soonest >= 0) timer_->start(int(soonest));
}

void UndoToast::expireDue()
{
    const size_t before = offers_.size();
    std::erase_if(offers_, [](const Offer& o) {
        return !o.deadline.isForever() && o.deadline.hasExpired();
    });
    if (offers_.size() != before) emit expired();
    if (!informing_) showTop();
    scheduleNextExpiry();
}

void UndoToast::showTop()
{
    if (informing_) return;   // the acknowledgement keeps the stage until it is done
    if (offers_.empty()) { hide(); return; }
    const Offer& top = offers_.back();
    const int behind = int(offers_.size()) - 1;
    // Spelled out, not %n: no translation is loaded, so Qt's plural forms print
    // "(s)" literally.
    message_->setText(behind == 0 ? top.message
                      : behind == 1 ? tr("%1  ·  1 more to undo").arg(top.message)
                                    : tr("%1  ·  %2 more to undo").arg(top.message).arg(behind));
    undoButton_->setVisible(true);
    layout()->setContentsMargins(18, 8, 8, 8);      // the pill sits as far from the edge as from top and bottom
    adjustSize();
    reposition();
    show();
    raise();
}

void UndoToast::enterEvent(QEnterEvent* e)
{
    hovered_ = true;
    pause();
    informTimer_->stop();
    QWidget::enterEvent(e);
}

void UndoToast::leaveEvent(QEvent* e)
{
    hovered_ = false;
    // While an acknowledgement is standing in front of the offers, it is the
    // acknowledgement's short timer that has to resume — starting the offers'
    // would leave the wrong message on screen for eight seconds.
    if (informing_) informTimer_->start(kInformMs);
    else resume();
    QWidget::leaveEvent(e);
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
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tokens::kMenuRadius, tokens::kMenuRadius);

    // Sits above the stack, so it needs its own surface rather than the card
    // fill — but still no shadow and no accent colour (SPEC.md §7).
    p.fillPath(path, palette().color(QPalette::Base));
    QColor border = palette().color(QPalette::Text);
    border.setAlpha(80);
    p.setPen(QPen(border, 1));
    p.drawPath(path);
}

}  // namespace napkin
