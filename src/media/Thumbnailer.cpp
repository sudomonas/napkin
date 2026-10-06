#include "Thumbnailer.h"
#include "BlobStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPixmapCache>
#include <QTimer>
#ifdef __GLIBC__
#  include <malloc.h>
#endif

namespace napkin {
namespace {

// Runs on any thread: touches files and QImage only, never a QPixmap.
QImage render(const QString& blobPath, const QString& thumbPath, int maxSize)
{
    if (!QFile::exists(blobPath)) return {};
    QImageReader reader(blobPath);
    reader.setAutoTransform(true);
    const QSize full = reader.size();   // the header only; nothing is decoded yet

    if (QFile::exists(thumbPath)) {
        QImage cached(thumbPath);
        // Napkin 0.1.7 and earlier scaled small images UP into their previews,
        // under the same file name. Such a preview is larger than its source:
        // remade, not served — it was blurry, and a few pixels off the size the
        // card had reserved for it.
        const bool upscaled = full.isValid()
            && (cached.width() > full.width() || cached.height() > full.height());
        if (!cached.isNull() && !upscaled) return cached;
    }

    // Scaled during decode, so a 4000x3000 photo never lands in memory whole.
    // Never scaled UP: a 200x140 favicon stored as a 920x644 preview was pure
    // cost, and the card draws it at its natural size anyway.
    if (full.isValid() && (full.width() > maxSize || full.height() > maxSize))
        reader.setScaledSize(full.scaled(maxSize, maxSize, Qt::KeepAspectRatio));

    QImage image = reader.read();
    if (image.isNull()) return {};
    // Qt's PNG handler ignores setScaledSize, so make sure the result is the
    // size that was asked for whatever the decoder did.
    if (image.width() > maxSize || image.height() > maxSize)
        image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QDir().mkpath(QFileInfo(thumbPath).absolutePath());
    if (image.save(thumbPath, "PNG"))
        QFile::setPermissions(thumbPath, QFile::ReadOwner | QFile::WriteOwner);
    return image;
}

}  // namespace

Thumbnailer::Thumbnailer(QString cacheDir, BlobStore& blobs, QObject* parent)
    : QObject(parent), cacheDir_(std::move(cacheDir)), blobs_(blobs)
{
    // Two, not one per core: each decode of a large image holds tens of MB
    // transiently, and the UI only ever wants the dozen cards on screen.
    pool_.setMaxThreadCount(2);

    // Decoding hundreds of multi-megapixel images leaves glibc holding ~1 GB of
    // freed arena memory that the application is not using (SPEC.md §12). It
    // is what a system monitor shows, so hand it back once the work is done.
    trim_ = new QTimer(this);
    trim_->setSingleShot(true);
    trim_->setInterval(1500);
    connect(trim_, &QTimer::timeout, this, [] {
#ifdef __GLIBC__
        malloc_trim(0);
#endif
    });
}

Thumbnailer::~Thumbnailer()
{
    pool_.clear();          // queued work that has not started is simply dropped
    pool_.waitForDone();    // a worker may be writing into cacheDir right now
}

QString Thumbnailer::cachePath(const QString& hash, int maxSize) const
{
    return QStringLiteral("%1/%2/%3_%4.png").arg(cacheDir_, hash.left(2), hash).arg(maxSize);
}

QString Thumbnailer::cacheKey(const QString& hash, int maxSize)
{
    return QStringLiteral("napkin_thumb_%1_%2").arg(hash).arg(maxSize);
}

QPixmap Thumbnailer::forBlob(const QString& hash, const QString& mime, int maxSize)
{
    if (hash.isEmpty()) return {};
    const QString key = cacheKey(hash, maxSize);
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) return cached;
    if (failed_.contains(key)) return {};

    const QImage image = render(blobs_.pathFor(hash, mime), cachePath(hash, maxSize), maxSize);
    if (image.isNull()) {
        // Remember the failure. Without this the delegate re-decodes on every
        // repaint, including the once-a-minute timestamp tick.
        failed_.insert(key);
        return {};
    }
    const QPixmap pixmap = QPixmap::fromImage(image);
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

Thumbnailer::State Thumbnailer::request(const QString& hash, const QString& mime, int maxSize,
                                        QPixmap* out)
{
    if (hash.isEmpty()) return State::Failed;
    const QString key = cacheKey(hash, maxSize);
    if (QPixmapCache::find(key, out)) return State::Ready;
    if (failed_.contains(key)) return State::Failed;
    if (pending_.contains(key)) return State::Pending;

    pending_.insert(key);
    const QString blobPath = blobs_.pathFor(hash, mime);
    const QString thumbPath = cachePath(hash, maxSize);
    // Newest first. A fast scroll queues work for cards that are already gone;
    // serving the queue in order would make the cards now on screen wait
    // behind every one of them.
    pool_.start([this, key, hash, maxSize, blobPath, thumbPath] {
        const QImage image = render(blobPath, thumbPath, maxSize);
        QMetaObject::invokeMethod(this, [this, key, hash, maxSize, image] {
            deliver(key, hash, maxSize, image);
        }, Qt::QueuedConnection);
    }, ++order_);
    return State::Pending;
}

void Thumbnailer::deliver(const QString& key, const QString& hash, int maxSize,
                          const QImage& image)
{
    pending_.remove(key);
    QPixmap pixmap;
    if (image.isNull()) failed_.insert(key);
    else QPixmapCache::insert(key, pixmap = QPixmap::fromImage(image));
    if (pending_.isEmpty()) trim_->start();
    emit ready(hash, maxSize, pixmap);
}

void Thumbnailer::waitForIdle()
{
    while (!pending_.isEmpty()) {
        pool_.waitForDone();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
}

}  // namespace napkin
