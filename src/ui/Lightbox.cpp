#include "Lightbox.h"

#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMovie>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

namespace napkin {

Lightbox::Lightbox(const QString& imagePath, bool animated, QString caption, QWidget* parent)
    : Lightbox({Image{imagePath, animated, std::move(caption)}}, 0, parent) {}

Lightbox::Lightbox(std::vector<Image> images, int start, QWidget* parent)
    : QDialog(parent), images_(std::move(images))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    view_ = new QLabel;
    view_->setAlignment(Qt::AlignCenter);
    view_->setMinimumSize(320, 240);
    layout->addWidget(view_);

    if (images_.empty()) images_.push_back({});
    index_ = std::clamp(start, 0, int(images_.size()) - 1);
    const QSize natural = load(images_[size_t(index_)]);

    // Sized once, for the image it opened on. Paging keeps the window where it
    // is and fits each picture into it: a window that jumped to every image's
    // own size would move the arrow keys out from under the hand.
    QSize target = natural.isValid() ? natural : QSize(640, 480);
    if (const auto* screen = QGuiApplication::primaryScreen())
        target = target.boundedTo(screen->availableGeometry().size() * 0.85);
    resize(target.expandedTo(QSize(480, 360)));
    rescale();
}

bool Lightbox::showImage(int index)
{
    if (index < 0 || index >= int(images_.size()) || index == index_) return false;
    index_ = index;
    load(images_[size_t(index_)]);
    rescale();
    return true;
}

// Returns the image's natural size, or an invalid size when it cannot be read.
QSize Lightbox::load(const Image& image)
{
    const QString caption = image.caption.isEmpty() ? tr("Image") : image.caption;
    setWindowTitle(images_.size() > 1
                       ? tr("%1 — %2 of %3").arg(caption).arg(index_ + 1).arg(images_.size())
                       : caption);

    if (movie_) {
        view_->setMovie(nullptr);
        delete movie_;
        movie_ = nullptr;
    }
    source_ = QPixmap();
    view_->clear();
    view_->setEnabled(true);

    QSize natural;
    if (image.path.isEmpty() || !QFile::exists(image.path)) {
        // Said where the picture would be, rather than skipped: skipping makes
        // the count in the title a lie.
        view_->setText(tr("This image is no longer on disk."));
        view_->setEnabled(false);
        return natural;
    }
    if (image.animated) {
        // QMovie decodes frame by frame, so a long animation never sits in
        // memory whole.
        movie_ = new QMovie(image.path, QByteArray(), this);
        movie_->setCacheMode(QMovie::CacheNone);
        natural = movie_->frameRect().size();
        if (!natural.isValid()) natural = QImageReader(image.path).size();
        view_->setMovie(movie_);
        movie_->start();
        return natural;
    }

    QImageReader reader(image.path);
    reader.setAutoTransform(true);
    natural = reader.size();
    // Bounded to the screen. Reading a 208 KB 8000x8000 PNG unbounded cost
    // 288 MB and 400 ms — a one-click memory spike on a double-click.
    QSize cap(3840, 2160);
    if (const auto* screen = QGuiApplication::primaryScreen())
        cap = screen->availableGeometry().size() * screen->devicePixelRatio();
    if (natural.isValid() && (natural.width() > cap.width() || natural.height() > cap.height())) {
        QSize target = natural;
        target.scale(cap, Qt::KeepAspectRatio);
        reader.setScaledSize(target);
    }
    source_ = QPixmap::fromImage(reader.read());
    if (source_.isNull()) {
        view_->setText(tr("Napkin could not open this image."));
        view_->setEnabled(false);
        return {};
    }
    return natural.isValid() ? natural : source_.size();
}

void Lightbox::rescale()
{
    if (movie_) {
        // Never upscale: a blurry enlargement is worse than honest small.
        QSize frame = movie_->frameRect().size();
        if (!frame.isValid()) return;
        movie_->setScaledSize(frame.scaled(view_->size(), Qt::KeepAspectRatio).boundedTo(frame));
        return;
    }
    if (source_.isNull()) return;
    const QSize target =
        source_.size().scaled(view_->size(), Qt::KeepAspectRatio).boundedTo(source_.size());
    view_->setPixmap(source_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void Lightbox::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    rescale();
}

void Lightbox::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape: accept(); return;
    // Ends stay put rather than wrapping, as the board's arrows do: a jump from
    // the last image to the first reads as having lost your place.
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown: showImage(index_ + 1); return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:   showImage(index_ - 1); return;
    case Qt::Key_Home:     showImage(0); return;
    case Qt::Key_End:      showImage(int(images_.size()) - 1); return;
    default: break;
    }
    QDialog::keyPressEvent(e);
}

}  // namespace napkin
