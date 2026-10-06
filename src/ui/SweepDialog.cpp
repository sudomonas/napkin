#include "SweepDialog.h"
#include "Tokens.h"

#include "../data/BufferRepository.h"
#include "../data/ItemRepository.h"
#include "../domain/BufferService.h"
#include "../domain/Clock.h"
#include "../domain/Preview.h"
#include "../domain/TimeFormat.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace napkin {

SweepDialog::SweepDialog(BufferRepository& buffers, ItemRepository& items, QWidget* parent)
    : QDialog(parent), buffers_(buffers), items_(items)
{
    setWindowTitle(tr("Clean up"));
    resize(560, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    list_ = new QListWidget;
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setAlternatingRowColors(false);
    layout->addWidget(list_, 1);

    auto* hint = new QLabel(
        tr("Everything ticked goes to the trash, where it stays for %1 days. "
           "Untick anything you want to keep.").arg(kTrashRetentionDays));
    hint->setWordWrap(true);
    QPalette dim = hint->palette();
    dim.setColor(QPalette::WindowText, tokens::text(palette(), tokens::kTextTertiary));
    hint->setPalette(dim);
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* sweep = buttons->addButton(tr("Move to trash"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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

        auto* row = new QListWidgetItem(
            QStringLiteral("%1\n%2")
                .arg(preview.primary.isEmpty() ? tr("Empty napkin") : preview.primary,
                     relativeTime(buffer.modifiedAt, nowMs())));
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        // Ticked by default: the user asked to clean up, and unticking what you
        // want to keep is less work than ticking everything you do not.
        row->setCheckState(Qt::Checked);
        row->setData(Qt::UserRole, QVariant::fromValue(buffer.id));
        list_->addItem(row);
    }

    QStringList parts;
    parts << (list_->count() == 1 ? tr("1 napkin older than %1 days")
                                  : tr("%1 napkins older than %2 days").arg(list_->count()))
                 .arg(kOlderThresholdDays);
    if (kept > 0) parts << tr("%n kept, and left alone", nullptr, kept);
    if (pinned > 0) parts << tr("%n pinned — pinning does not protect", nullptr, pinned);
    summary_->setText(parts.join(QStringLiteral("\n")));
}

int SweepDialog::candidates() const { return list_->count(); }

}  // namespace napkin
