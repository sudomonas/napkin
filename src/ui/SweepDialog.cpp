#include "SweepDialog.h"
#include "Tokens.h"

#include "../data/BufferRepository.h"
#include "../data/ItemRepository.h"
#include "../domain/BufferService.h"
#include "../domain/Clock.h"
#include "../domain/Preview.h"
#include "../domain/TimeFormat.h"

#include "SurfacePanel.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace napkin {

namespace {

constexpr int kDateRole = Qt::UserRole + 1;

// A row: tick, then the napkin's title over when it was last touched, in the
// quieter secondary colour. The stock two-line item set both lines in the same
// weight with no room between rows, and drew a focus line through the first.
class SweepRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return {option.rect.width(), option.fontMetrics.lineSpacing() * 2 + 20};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget* w = opt.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        const QRect r = opt.rect.adjusted(4, 2, -4, -2);

        // Hover only; the list has no selection.
        QStyleOptionViewItem panel(opt);
        panel.rect = opt.rect;
        panel.state &= ~QStyle::State_Selected;
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &panel, p, w);

        QStyleOptionButton tick;
        tick.initFrom(w);
        tick.rect = QRect(r.left() + 10, r.center().y() - 9, 18, 18);
        tick.state = QStyle::State_Enabled
                     | (index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? QStyle::State_On
                                                                               : QStyle::State_Off);
        style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &tick, p, w);

        const QRect text = r.adjusted(42, 8, -10, -8);
        const QFontMetrics fm(opt.font);
        p->save();
        p->setFont(opt.font);
        p->setPen(opt.palette.color(QPalette::Text));
        p->drawText(QRect(text.left(), text.top(), text.width(), fm.lineSpacing()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width()));
        p->setPen(tokens::text(opt.palette, tokens::kTextSecondary));
        p->drawText(QRect(text.left(), text.top() + fm.lineSpacing(), text.width(), fm.lineSpacing()),
                    Qt::AlignLeft | Qt::AlignVCenter, index.data(kDateRole).toString());
        p->restore();

        if (opt.state & QStyle::State_HasFocus) {
            QStyleOptionFocusRect ring;
            ring.initFrom(w);
            ring.rect = r.adjusted(4, 2, -4, -2);
            style->drawPrimitive(QStyle::PE_FrameFocusRect, &ring, p, w);
        }
    }

    // Anywhere on the row ticks it, not only the box: the row is the target.
    bool editorEvent(QEvent* e, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override
    {
        const bool click = e->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton
            && option.rect.contains(static_cast<QMouseEvent*>(e)->position().toPoint());
        const bool key = e->type() == QEvent::KeyPress
            && (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Space
                || static_cast<QKeyEvent*>(e)->key() == Qt::Key_Select);
        if (!click && !key) {
            // Swallow the press and double-click too, or the view acts on them.
            return e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick;
        }
        const bool on = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        return model->setData(index, on ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }
};

}  // namespace

SweepDialog::SweepDialog(BufferRepository& buffers, ItemRepository& items, QWidget* parent)
    : QDialog(parent), buffers_(buffers), items_(items)
{
    setWindowTitle(tr("Clean up"));
    resize(520, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(tokens::kHeaderGap - 8, tokens::kHeaderGap - 8,
                               tokens::kHeaderGap - 8, tokens::kWindowMargin + 4);
    layout->setSpacing(tokens::kGapTight);

    auto* heading = new QLabel(tr("Clean up old napkins"));
    heading->setFont(tokens::scaledBy(heading->font(), 1.15, QFont::DemiBold));
    layout->addWidget(heading);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    QPalette quiet = summary_->palette();
    quiet.setColor(QPalette::WindowText, tokens::text(quiet, tokens::kTextSecondary));
    summary_->setPalette(quiet);
    layout->addWidget(summary_);
    layout->addSpacing(tokens::kGapTight);

    // The rows sit on a surface, as the main window's list and Settings'
    // pages do.
    auto* surface = new SurfacePanel;
    auto* surfaceColumn = new QVBoxLayout(surface);
    surfaceColumn->setContentsMargins(4, 4, 4, 4);
    list_ = new QListWidget;
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setAutoFillBackground(false);
    list_->viewport()->setAutoFillBackground(false);
    list_->setItemDelegate(new SweepRowDelegate(list_));
    list_->setAccessibleName(tr("Napkins to move to the trash"));
    surfaceColumn->addWidget(list_);
    layout->addWidget(surface, 1);

    auto* hint = new QLabel(
        tr("Ticked napkins go to the trash for %1 days. Untick any you want to keep.")
            .arg(BufferService::trashRetentionDays()));
    hint->setWordWrap(true);
    hint->setPalette(quiet);
    layout->addSpacing(4);
    layout->addWidget(hint);
    layout->addSpacing(tokens::kWindowMargin);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(tokens::kGapTight);
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    auto* sweep = new QPushButton(tr("Move to trash"));
    sweep->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(sweep);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    // "Move to trash" with nothing ticked would do nothing, so it says so by
    // being unavailable rather than by being the highlighted default.
    const auto refresh = [this, sweep] {
        bool any = false;
        for (int i = 0; i < list_->count() && !any; ++i)
            any = list_->item(i)->checkState() == Qt::Checked;
        sweep->setEnabled(any);
    };
    connect(list_, &QListWidget::itemChanged, this, refresh);
    connect(sweep, &QPushButton::clicked, this, [this] {
        accepted_.clear();
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* row = list_->item(i);
            if (row->checkState() == Qt::Checked)
                accepted_ << row->data(Qt::UserRole).value<BufferId>();
        }
        accept();
    });

    populate();
    refresh();   // after populate(): adding rows does not emit itemChanged
}

void SweepDialog::populate()
{
    const Timestamp cutoff = BufferService::olderThanCutoff();
    int kept = 0, pinned = 0;

    for (const auto& buffer : buffers_.listLive(5000)) {
        if (buffer.modifiedAt >= cutoff) continue;
        if (buffer.kept) { ++kept; continue; }   // kept is exactly this promise
        if (buffer.pinned) ++pinned;             // pinned is placement; it does not protect

        const auto counts = items_.countsForBuffer(buffer.id);
        const auto preview = derivePreview(items_.previewHead(buffer.id),
                                           counts.total, counts.images, {}, buffer.name);

        auto* row = new QListWidgetItem(preview.primary.isEmpty() ? tr("Empty napkin")
                                                                 : preview.primary);
        row->setData(kDateRole, tr("Last changed %1").arg(relativeTime(buffer.modifiedAt, nowMs())));
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        // Ticked by default: the user asked to clean up, and unticking what you
        // want to keep is less work than ticking everything you do not.
        row->setCheckState(Qt::Checked);
        row->setData(Qt::UserRole, QVariant::fromValue(buffer.id));
        list_->addItem(row);
    }

    QStringList parts;
    // The user's threshold, not the default: this said 30 whatever Settings
    // said (audit, 2026-10-09).
    parts << (list_->count() == 1 ? tr("1 napkin untouched for %1+ days")
                                  : tr("%1 napkins untouched for %2+ days").arg(list_->count()))
                 .arg(BufferService::olderThanDays());
    if (kept > 0) parts << tr("%n kept, so left alone", nullptr, kept);
    if (pinned > 0) parts << tr("%n pinned (pinning doesn't protect)", nullptr, pinned);
    summary_->setText(parts.join(QStringLiteral(" · ")));
}

int SweepDialog::candidates() const { return list_->count(); }

}  // namespace napkin
