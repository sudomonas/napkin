#pragma once
#include "../domain/Types.h"
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <memory>
#include <vector>

namespace napkin {

class Database;

// Writes edits to existing notes on a worker, over a second connection to the
// same database file.
//
// Saving a 10 MB note costs ~140 ms and a 50 MB one ~700 ms — SQLite rewrites
// the row and the full-text index tokenises the old text out and the new text
// in — and autosave runs every two seconds during continuous typing, so on the
// UI thread that was a regular freeze in the middle of typing.
//
// Deliberately narrow (SPEC.md §8): only the timed autosave of notes that
// already have rows comes here. Every other save — leaving a card, switching
// napkins, losing focus, quitting — first waits for this to be idle and then
// writes synchronously, so nothing that relies on "the text has landed" sees a
// difference. Jobs run one at a time, in order, so an older snapshot can never
// land after a newer one.
class BackgroundSaver : public QObject {
    Q_OBJECT
public:
    struct Entry {
        ItemId  id;
        QString text;
        quint64 generation;   // the card's edit count when this was taken
    };

    // Unavailable over an in-memory database, which a second connection cannot
    // see; callers then save synchronously as before.
    explicit BackgroundSaver(const Database& db, QObject* parent = nullptr);
    ~BackgroundSaver() override;

    bool isAvailable() const { return !path_.isEmpty(); }
    bool isBusy() const { return inFlight_ > 0; }

    void save(BufferId buffer, std::vector<Entry> entries);
    // Blocks until every queued save has been written and reported.
    void waitForIdle();

signals:
    void saved(BufferId buffer, const std::vector<BackgroundSaver::Entry>& entries,
               Timestamp when);
    void failed(const QString& reason);

private:
    QString     path_;
    QThreadPool pool_;
    int         inFlight_ = 0;
    // Opened on first use, by whichever pool thread runs the job; the pool has
    // one thread at most, so it is never used by two at once.
    std::shared_ptr<Database> connection_;
};

}  // namespace napkin
