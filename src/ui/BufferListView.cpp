#include "BufferListView.h"
#include "BufferCardDelegate.h"
#include "BufferListModel.h"

#include "../media/BlobStore.h"
#include "../media/Thumbnailer.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QMouseEvent>
#include <QMovie>
#include <QKeyEvent>
#include <QScrollBar>

namespace napkin {

BufferListView::BufferListView(Thumbnailer& thumbs, BlobStore& blobs, QWidget* parent)
    : QListView(parent), blobs_(&blobs)
{
    delegate_ = new BufferCardDelegate(this);
    setItemDelegate(delegate_);

    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);          // for the subtle hover state
    setUniformItemSizes(false);
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_MacShowFocusRect, false);

    delegate_->setThumbnailer(&thumbs);
    connect(&thumbs, &Thumbnailer::ready, viewport(), qOverload<>(&QWidget::update));
}

// A single click only selects, so a card can be picked up and acted on — pinned,
// kept, deleted — without being opened first. Opening is a deliberate second
// gesture: double-click, or Enter.
void BufferListView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QModelIndex index = indexAt(e->pos());
    if (index.isValid()) {
        setCurrentIndex(index);
        emit rowActivated(index.row());
        return;
    }
    QListView::mouseDoubleClickEvent(e);
}

void BufferListView::contextMenuEvent(QContextMenuEvent* e)
{
    const QModelIndex index = indexAt(e->pos());
    if (!index.isValid()) return;
    setCurrentIndex(index);
    emit contextMenuRequested(index.row(), e->globalPos());
}

void BufferListView::mouseMoveEvent(QMouseEvent* e)
{
    QListView::mouseMoveEvent(e);
    updateHoverAnimation(indexAt(e->pos()));
}

void BufferListView::leaveEvent(QEvent* e)
{
    stopHoverAnimation();
    QListView::leaveEvent(e);
}

// An animated image plays while the pointer is over its card, so a GIF is
// visibly a GIF without twelve of them running at once (SPEC.md §12).
void BufferListView::updateHoverAnimation(const QModelIndex& index)
{
    if (!index.isValid() || !blobs_) { stopHoverAnimation(); return; }
    if (index.row() == hoverRow_) return;

    stopHoverAnimation();

    if (!index.data(BufferListModel::ThumbAnimatedRole).toBool()) return;
    const QString hash = index.data(BufferListModel::ThumbHashRole).toString();
    const QString mime = index.data(BufferListModel::ThumbMimeRole).toString();
    if (hash.isEmpty()) return;

    hoverRow_ = index.row();
    hoverMovie_ = new QMovie(blobs_->pathFor(hash, mime), QByteArray(), this);
    hoverMovie_->setCacheMode(QMovie::CacheNone);
    hoverMovie_->setScaledSize(QSize(BufferCardDelegate::kThumbSize * 2,
                                     BufferCardDelegate::kThumbSize * 2));
    connect(hoverMovie_, &QMovie::frameChanged, this, [this] {
        if (hoverRow_ < 0 || !hoverMovie_) return;
        delegate_->setAnimationFrame(hoverRow_, hoverMovie_->currentPixmap());
        update(model()->index(hoverRow_, 0));
    });
    hoverMovie_->start();
}

void BufferListView::stopHoverAnimation()
{
    if (hoverMovie_) {
        hoverMovie_->stop();
        hoverMovie_->deleteLater();
        hoverMovie_ = nullptr;
    }
    if (hoverRow_ >= 0) {
        const int row = hoverRow_;
        hoverRow_ = -1;
        delegate_->clearAnimationFrame();
        if (model() && row < model()->rowCount()) update(model()->index(row, 0));
    }
}

void BufferListView::keyPressEvent(QKeyEvent* e)
{
    // List-scope keys only fire when the list itself has focus, so they can
    // never collide with typing (SPEC.md §7).
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (currentIndex().isValid()) { emit rowActivated(currentIndex().row()); return; }
        break;
    default:
        break;
    }

    // Bare letters are safe as commands precisely because this handler only
    // runs with list focus; Delete rather than Ctrl+D, which sits next to
    // Ctrl+N and is far too easy to hit by accident.
    if (currentIndex().isValid() && e->modifiers() == Qt::NoModifier) {
        const int row = currentIndex().row();
        switch (e->key()) {
        // P and K are gone. Once typing started notes everywhere else, a
        // bare letter that silently pinned or kept a napkin was a trap: typing
        // "pack" into the list pinned AND kept it (second usability test).
        // Pin and Keep are Ctrl+P and Ctrl+D, window-wide, with a toast.
        case Qt::Key_Delete: emit trashRequested(row);      return;
        case Qt::Key_R:      emit restoreRequested(row);    return;
        default: break;
        }
    }
    QListView::keyPressEvent(e);
}

}  // namespace napkin
