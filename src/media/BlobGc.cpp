#include "BlobGc.h"
#include "BlobStore.h"
#include "../data/ItemRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace napkin {

BlobScan scanBlobFiles(const QString& blobRoot, const QString& thumbnailDir,
                       const std::atomic_bool* cancelled)
{
    BlobScan scan;
    auto stop = [cancelled] { return cancelled && cancelled->load(); };

    QDirIterator it(blobRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && !stop()) {
        it.next();
        const QFileInfo info(it.fileInfo());
        // A .tmp survives only when a write was interrupted: it was never
        // renamed into place, so nothing can reference it.
        if (info.suffix() == QLatin1String("tmp")) scan.temporaries.push_back(info.absoluteFilePath());
        else scan.blobFiles.push_back(info.absoluteFilePath());
    }

    if (!thumbnailDir.isEmpty()) {
        QDirIterator thumbs(thumbnailDir, QDir::Files, QDirIterator::Subdirectories);
        while (thumbs.hasNext() && !stop()) {
            thumbs.next();
            scan.thumbnails.push_back(thumbs.fileInfo().absoluteFilePath());
        }
    }
    return scan;
}

GcResult applySweep(const BlobScan& scan, ItemRepository& items,
                    const QSet<QString>& protectedHashes)
{
    GcResult result;

    QSet<QString> referenced = protectedHashes;
    for (const auto& hash : items.allBlobHashes()) referenced.insert(hash);

    // Safe to remove here and not on the walking thread: blobs are written on
    // this thread, synchronously, so no write is half-done while this runs.
    for (const auto& path : scan.temporaries)
        if (QFile::remove(path)) ++result.temporariesRemoved;

    for (const auto& path : scan.blobFiles)
        if (!referenced.contains(QFileInfo(path).completeBaseName()) && QFile::remove(path))
            ++result.orphansRemoved;

    // Thumbnails are renderings of the same private content and must not
    // outlive it. Emptying the trash previously reclaimed the blob and left a
    // picture of it in the data directory for ever.
    for (const auto& path : scan.thumbnails) {
        // Named "<hash>_<size>.png".
        const QString hash = QFileInfo(path).completeBaseName().section(QLatin1Char('_'), 0, 0);
        if (!referenced.contains(hash) && QFile::remove(path)) ++result.thumbnailsRemoved;
    }
    return result;
}

GcResult reconcileBlobs(ItemRepository& items, BlobStore& blobs, const QString& thumbnailDir,
                        const QSet<QString>& protectedHashes)
{
    GcResult result = applySweep(scanBlobFiles(blobs.rootDir(), thumbnailDir), items,
                                 protectedHashes);

    // The other direction: rows pointing at files that are gone.
    for (const auto& item : items.allImageItems())
        if (!blobs.exists(item.blobHash, item.mime))
            result.missingBlobs.push_back(item.blobHash);

    return result;
}

// --- in the background ---------------------------------------------------------

BlobSweeper::BlobSweeper(ItemRepository& items, BlobStore& blobs, QString thumbnailDir,
                         QObject* parent)
    : QObject(parent), items_(items), blobs_(blobs), thumbnailDir_(std::move(thumbnailDir))
{
    pool_.setMaxThreadCount(1);
}

BlobSweeper::~BlobSweeper()
{
    // A walk in progress is abandoned, not finished: nothing has been deleted
    // yet, and the next startup sweeps again.
    cancelled_->store(true);
    pool_.waitForDone();
}

void BlobSweeper::setProtectedHashes(std::function<QSet<QString>()> provider)
{
    protected_ = std::move(provider);
}

void BlobSweeper::start()
{
    if (running_) { again_ = true; return; }
    running_ = true;
    const QString root = blobs_.rootDir();
    const QString thumbs = thumbnailDir_;
    const auto cancelled = cancelled_;
    pool_.start([this, root, thumbs, cancelled] {
        BlobScan scan = scanBlobFiles(root, thumbs, cancelled.get());
        if (cancelled->load()) return;
        QMetaObject::invokeMethod(this, [this, scan = std::move(scan)] { apply(scan); },
                                  Qt::QueuedConnection);
    });
}

void BlobSweeper::apply(const BlobScan& scan)
{
    last_ = applySweep(scan, items_, protected_ ? protected_() : QSet<QString>());
    running_ = false;
    if (again_) {
        again_ = false;
        start();
        return;
    }
    emit finished();
}

void BlobSweeper::waitForIdle()
{
    while (running_) {
        pool_.waitForDone();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
}

}  // namespace napkin
