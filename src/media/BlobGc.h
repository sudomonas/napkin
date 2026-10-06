#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace napkin {

class BlobStore;
class ItemRepository;

// Reconciliation, run at startup and after anything that hard-deletes rows.
// The write ordering (invariants 6 and 7) deliberately biases toward orphan
// blobs, so something has to collect them; and a row whose blob has gone
// missing must be reported rather than silently rendered blank (SPEC.md §8).
struct GcResult {
    int orphansRemoved = 0;   // files on disk that nothing references
    int thumbnailsRemoved = 0;   // renderings of images that no longer exist
    int temporariesRemoved = 0;   // .tmp files from an interrupted write
    std::vector<QString> missingBlobs;  // referenced hashes with no file
};

// `protectedHashes` are blobs a live undo offer still depends on: their rows are
// already deleted, so the sweep would see them as orphans and reclaim the very
// files undo is about to restore. Relying on every caller to dismiss the offer
// first is not a guarantee — one forgotten call site destroys user data — so the
// protection travels with the sweep instead.
GcResult reconcileBlobs(ItemRepository& items, BlobStore& blobs,
                        const QString& thumbnailDir = {},
                        const QSet<QString>& protectedHashes = {});

// The same sweep in two halves, so the slow half can leave the UI thread.
//
// Walking the directories is the part that grows with the store; it touches
// only the filesystem and runs anywhere. DECIDING what to delete must not run
// on a snapshot: a paste between the walk and the decision can make a listed
// orphan referenced again — content addressing means pasting the same image
// reuses the file — and deleting it then loses the picture. So the decision
// runs on the thread that writes rows, against the references as they are at
// that moment, and only ever about files the walk saw.
struct BlobScan {
    std::vector<QString> blobFiles;     // absolute paths
    std::vector<QString> temporaries;   // interrupted writes
    std::vector<QString> thumbnails;
};
BlobScan scanBlobFiles(const QString& blobRoot, const QString& thumbnailDir,
                       const std::atomic_bool* cancelled = nullptr);
GcResult applySweep(const BlobScan& scan, ItemRepository& items,
                    const QSet<QString>& protectedHashes);

// Runs scanBlobFiles on a worker and applySweep back on the owner's thread.
// Emptying a trash full of screenshots used to stall the window for as long as
// it took to walk the whole store; so did every startup.
class BlobSweeper : public QObject {
    Q_OBJECT
public:
    BlobSweeper(ItemRepository& items, BlobStore& blobs, QString thumbnailDir,
                QObject* parent = nullptr);
    ~BlobSweeper() override;

    // Read when the sweep is applied, not when it starts: an undo offer made
    // while the walk was running must still protect its blobs.
    void setProtectedHashes(std::function<QSet<QString>()> provider);

    // Starts a sweep. One already running is not doubled; it is followed by
    // exactly one more, because what it walked may predate this request.
    void start();
    bool isRunning() const { return running_; }
    // For tests: blocks until no sweep is running or queued.
    void waitForIdle();
    const GcResult& lastResult() const { return last_; }

signals:
    void finished();

private:
    void apply(const BlobScan& scan);

    ItemRepository& items_;
    BlobStore&      blobs_;
    QString         thumbnailDir_;
    std::function<QSet<QString>()> protected_;
    QThreadPool     pool_;
    std::shared_ptr<std::atomic_bool> cancelled_ = std::make_shared<std::atomic_bool>(false);
    bool running_ = false;
    bool again_ = false;
    GcResult last_;
};

}  // namespace napkin
