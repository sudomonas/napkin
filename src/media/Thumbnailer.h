#pragma once
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QThreadPool>

class QTimer;

namespace napkin {

class BlobStore;

// Card thumbnails. Generated once, cached on disk, then held in Qt's shared
// pixmap cache — §12 forbids loading full-resolution images just to draw a list.
//
// Two ways in. request() never blocks: it answers from memory or queues the
// work for a worker thread and says so, and ready() fires when the picture
// exists. That is what anything painting on the UI thread uses — decoding a
// 2560x1440 PNG to make a board preview costs ~100 ms, and a board scrolled
// through 500 of them froze the window for 14 seconds. forBlob() is the
// blocking form, for callers that need the pixel data now and are not drawing.
class Thumbnailer : public QObject {
    Q_OBJECT
public:
    static constexpr int kCardSize = 96;  // logical pixels, square bounding box

    Thumbnailer(QString cacheDir, BlobStore& blobs, QObject* parent = nullptr);
    // Waits for work in flight: a worker writes into cacheDir.
    ~Thumbnailer() override;

    // Null pixmap when the blob is missing; callers draw a placeholder rather
    // than nothing, so a vanished file is visible instead of silent.
    QPixmap forBlob(const QString& hash, const QString& mime, int maxSize = kCardSize);

    enum class State { Ready, Pending, Failed };
    // Never blocks. Ready fills *out; Pending means ready(hash, maxSize) will
    // follow, whether the work succeeds or not; Failed is final.
    State request(const QString& hash, const QString& mime, int maxSize, QPixmap* out);

    // Blocks until nothing is queued or in flight, and delivers the results.
    // For tests and the benchmark; the application never needs to wait.
    void waitForIdle();
    bool isIdle() const { return pending_.isEmpty(); }
    QString cacheDir() const { return cacheDir_; }

signals:
    // A request() that answered Pending has finished; a null pixmap means it
    // failed. The picture travels with the signal rather than being fetched
    // again: the shared pixmap cache holds about four board previews, so asking
    // again could find it already evicted and queue the same work for ever.
    void ready(const QString& hash, int maxSize, const QPixmap& pixmap);

private:
    QString cachePath(const QString& hash, int maxSize) const;
    static QString cacheKey(const QString& hash, int maxSize);
    void deliver(const QString& key, const QString& hash, int maxSize, const QImage& image);

    QString    cacheDir_;
    BlobStore& blobs_;
    QSet<QString> failed_;    // blobs that could not be rendered; do not retry
    QSet<QString> pending_;   // queued or in flight, so a repaint does not queue twice
    QThreadPool   pool_;
    int           order_ = 0;   // newest request first; see request()
    QTimer*       trim_ = nullptr;
};

}  // namespace napkin
